#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#include "common/selector.h"
#include "metric/namespace.h"
#include "metric/labels.h"
#include "events/context_arg.h"
#include "common/http.h"
#include "common/logs.h"
#include "common/validator.h"
#include "common/reject.h"
#include "cluster/pass.h"
#include "parsers/metric_types.h"
#include "parsers/http_proto.h"
#include "main.h"

#define OTLP_KEY_MAX 256
#define OTLP_VAL_MAX 1024
#define OTLP_NAME_MAX 1024

static void otlp_sanitize_attr_key(char *k, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)k[i];
		if (isalpha(c) || isdigit(c) || c == '_' || c == ':' || c == '.' || c == '-')
			continue;
		k[i] = '_';
	}
}

static int otlp_ingest_value_labels(char *metric_name, double value, alligator_ht *resource_lbl, alligator_ht *dp_lbl, context_arg *carg)
{
	alligator_ht *lbl = NULL;

	if (resource_lbl)
		lbl = labels_dup(resource_lbl);
	if (dp_lbl)
	{
		if (lbl)
			labels_merge(lbl, dp_lbl);
		else
			lbl = labels_dup(dp_lbl);
	}

	if (cluster_pass(carg, metric_name, lbl, &value, DATATYPE_DOUBLE, METRIC_TYPE_UNTYPED))
	{
		labels_hash_free(lbl);
		return 1;
	}
	metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
	return 1;
}

static int otlp_anyvalue_to_buf(json_t *wrap, char *buf, size_t bufsz)
{
	json_t *v;

	if (!wrap || !json_is_object(wrap))
		return 0;
	v = json_object_get(wrap, "stringValue");
	if (v && json_is_string(v))
	{
		const char *s = json_string_value(v);
		if (strlcpy(buf, s ? s : "", bufsz) >= bufsz)
			return 0;
		return 1;
	}
	v = json_object_get(wrap, "boolValue");
	if (v && json_is_boolean(v))
	{
		snprintf(buf, bufsz, "%s", json_is_true(v) ? "true" : "false");
		return 1;
	}
	v = json_object_get(wrap, "intValue");
	if (v && json_is_integer(v))
	{
		snprintf(buf, bufsz, "%" JSON_INTEGER_FORMAT, json_integer_value(v));
		return 1;
	}
	v = json_object_get(wrap, "doubleValue");
	if (v && json_is_real(v))
	{
		snprintf(buf, bufsz, "%.17g", json_real_value(v));
		return 1;
	}
	return 0;
}

static alligator_ht *otlp_kv_array_to_labels(json_t *attrs, context_arg *carg)
{
	size_t n, added = 0;
	alligator_ht *lbl;

	if (!attrs || !json_is_array(attrs))
		return NULL;
	n = json_array_size(attrs);
	if (!n)
		return NULL;

	lbl = alligator_ht_init(NULL);
	for (size_t i = 0; i < n; i++)
	{
		json_t *kv = json_array_get(attrs, i);
		json_t *jkey, *jwrap;
		const char *ks;
		char kbuf[OTLP_KEY_MAX];
		char vbuf[OTLP_VAL_MAX];
		size_t kl;

		if (!json_is_object(kv))
			continue;
		jkey = json_object_get(kv, "key");
		ks = json_is_string(jkey) ? json_string_value(jkey) : NULL;
		if (!ks)
			continue;
		kl = strlcpy(kbuf, ks, sizeof(kbuf));
		if (kl >= sizeof(kbuf))
			continue;
		otlp_sanitize_attr_key(kbuf, strlen(kbuf));
		if (!metric_label_validator(kbuf, strlen(kbuf)))
			continue;
		jwrap = json_object_get(kv, "value");
		if (!otlp_anyvalue_to_buf(jwrap, vbuf, sizeof(vbuf)))
			continue;
		metric_label_value_validator_normalizer(vbuf, strlen(vbuf));
		if (carg && reject_metric(carg->reject, kbuf, vbuf))
			continue;
		labels_hash_insert_nocache(lbl, kbuf, vbuf);
		added++;
	}
	if (!added)
	{
		labels_hash_free(lbl);
		return NULL;
	}
	return lbl;
}

/* OTLP JSON normally uses number types; some encoders emit quoted numbers ("asInt":"42"). */
static int otlp_json_scalar_to_double(json_t *j, double *out)
{
	const char *s;
	char *end;

	if (!j)
		return 0;
	if (json_is_real(j))
	{
		*out = json_real_value(j);
		return 1;
	}
	if (json_is_integer(j))
	{
		*out = (double)json_integer_value(j);
		return 1;
	}
	if (json_is_string(j))
	{
		s = json_string_value(j);
		if (!s || !*s)
			return 0;
		*out = strtod(s, &end);
		if (end == s || (end && *end != '\0'))
			return 0;
		return 1;
	}
	return 0;
}

static int otlp_number_datapoint_value(json_t *dp, double *out)
{
	json_t *j;

	if (!dp || !json_is_object(dp))
		return 0;
	j = json_object_get(dp, "asDouble");
	if (j && otlp_json_scalar_to_double(j, out))
		return 1;
	j = json_object_get(dp, "asInt");
	if (j && otlp_json_scalar_to_double(j, out))
		return 1;
	j = json_object_get(dp, "doubleValue");
	if (j && otlp_json_scalar_to_double(j, out))
		return 1;
	j = json_object_get(dp, "intValue");
	if (j && otlp_json_scalar_to_double(j, out))
		return 1;
	return 0;
}

static uint8_t otlp_ingest_one_datapoint(char *metric_name, json_t *dp, alligator_ht *resource_lbl, context_arg *carg)
{
	alligator_ht *dp_lbl = NULL;
	json_t *dp_attrs;
	double value;

	if (!otlp_number_datapoint_value(dp, &value))
		return 0;

	dp_attrs = json_object_get(dp, "attributes");
	if (dp_attrs && json_is_array(dp_attrs) && json_array_size(dp_attrs) > 0)
		dp_lbl = otlp_kv_array_to_labels(dp_attrs, carg);

	otlp_ingest_value_labels(metric_name, value, resource_lbl, dp_lbl, carg);
	labels_hash_free(dp_lbl);
	return 1;
}

static void otlp_ingest_metric_object(json_t *metric, alligator_ht *resource_lbl, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	json_t *jname, *wrap, *dps;
	const char *name_raw;
	char metric_name[OTLP_NAME_MAX];
	size_t name_len;
	size_t ndp, i;

	if (!metric || !json_is_object(metric))
		return;
	jname = json_object_get(metric, "name");
	name_raw = json_is_string(jname) ? json_string_value(jname) : NULL;
	if (!name_raw)
		return;
	name_len = strlcpy(metric_name, name_raw, sizeof(metric_name));
	if (name_len >= sizeof(metric_name))
	{
		(*bad)++;
		return;
	}
	if (!name_len)
		return;

	wrap = json_object_get(metric, "gauge");
	if (!wrap)
		wrap = json_object_get(metric, "sum");
	if (!wrap || !json_is_object(wrap))
		return;
	dps = json_object_get(wrap, "dataPoints");
	if (!dps || !json_is_array(dps))
		return;
	ndp = json_array_size(dps);
	if (!metric_name_validator_promstatsd(metric_name, name_len))
	{
		*bad += ndp;
		return;
	}
	metric_name_normalizer(metric_name, name_len);
	for (i = 0; i < ndp; i++)
	{
		json_t *dp = json_array_get(dps, i);
		if (otlp_ingest_one_datapoint(metric_name, dp, resource_lbl, carg))
		{
			(*ok)++;
			carg->push_accepted_lines++;
			carg->parser_status = 1;
		}
		else
			(*bad)++;
	}
}

static void otlp_ingest_resource_metrics_json(json_t *root, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	json_t *rms = json_object_get(root, "resourceMetrics");
	size_t nrm, i;

	if (!rms || !json_is_array(rms))
		return;
	nrm = json_array_size(rms);
	for (i = 0; i < nrm; i++)
	{
		json_t *rm = json_array_get(rms, i);
		json_t *resource, *sms;
		alligator_ht *res_lbl = NULL;
		size_t ns, j;

		if (!json_is_object(rm))
			continue;
		resource = json_object_get(rm, "resource");
		if (resource && json_is_object(resource))
			res_lbl = otlp_kv_array_to_labels(json_object_get(resource, "attributes"), carg);
		sms = json_object_get(rm, "scopeMetrics");
		if (sms && json_is_array(sms))
		{
			ns = json_array_size(sms);
			for (j = 0; j < ns; j++)
			{
				json_t *sm = json_array_get(sms, j);
				json_t *metrics;
				size_t nm, k;

				if (!json_is_object(sm))
					continue;
				metrics = json_object_get(sm, "metrics");
				if (!metrics || !json_is_array(metrics))
					continue;
				nm = json_array_size(metrics);
				for (k = 0; k < nm; k++)
					otlp_ingest_metric_object(json_array_get(metrics, k), res_lbl, carg, ok, bad);
			}
		}
		labels_hash_free(res_lbl);
	}
}

static int otlp_pb_read_varint(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
	uint64_t v = 0;
	unsigned shift = 0;
	while (*p < end && shift < 64)
	{
		uint8_t b = *(*p)++;
		v |= (uint64_t)(b & 0x7f) << shift;
		if (!(b & 0x80))
		{
			*out = v;
			return 1;
		}
		shift += 7;
	}
	return 0;
}

static int otlp_pb_read_fixed64(const uint8_t **p, const uint8_t *end, uint64_t *out)
{
	uint64_t v = 0;
	if ((size_t)(end - *p) < 8)
		return 0;
	for (int i = 0; i < 8; i++)
		v |= ((uint64_t)(*p)[i]) << (8 * i);
	*p += 8;
	*out = v;
	return 1;
}

static int otlp_pb_read_len(const uint8_t **p, const uint8_t *end, const uint8_t **start, size_t *len)
{
	uint64_t l;
	if (!otlp_pb_read_varint(p, end, &l))
		return 0;
	if (l > (uint64_t)(end - *p))
		return 0;
	*start = *p;
	*len = (size_t)l;
	*p += l;
	return 1;
}

static int otlp_pb_skip_field(const uint8_t **p, const uint8_t *end, uint8_t wire)
{
	uint64_t t;
	const uint8_t *s;
	size_t l;

	switch (wire)
	{
	case 0:
		return otlp_pb_read_varint(p, end, &t);
	case 1:
		return otlp_pb_read_fixed64(p, end, &t);
	case 2:
		return otlp_pb_read_len(p, end, &s, &l);
	case 5:
		if ((size_t)(end - *p) < 4)
			return 0;
		*p += 4;
		return 1;
	default:
		return 0;
	}
}

static int otlp_pb_anyvalue_to_buf(const uint8_t *p, size_t len, char *buf, size_t bufsz)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	while (cur < end)
	{
		uint64_t tagv, v;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;

		if (!otlp_pb_read_varint(&cur, end, &tagv))
			return 0;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);

		if (tag == 1 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				return 0;
			if (sl >= bufsz)
				return 0;
			memcpy(buf, s, sl);
			buf[sl] = '\0';
			return 1;
		}
		if (tag == 2 && wire == 0)
		{
			if (!otlp_pb_read_varint(&cur, end, &v))
				return 0;
			snprintf(buf, bufsz, "%s", v ? "true" : "false");
			return 1;
		}
		if (tag == 3 && wire == 0)
		{
			if (!otlp_pb_read_varint(&cur, end, &v))
				return 0;
			snprintf(buf, bufsz, "%" d64, (int64_t)v);
			return 1;
		}
		if (tag == 4 && wire == 1)
		{
			uint64_t u;
			double d;
			if (!otlp_pb_read_fixed64(&cur, end, &u))
				return 0;
			memcpy(&d, &u, sizeof(d));
			snprintf(buf, bufsz, "%.17g", d);
			return 1;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			return 0;
	}
	return 0;
}

static int otlp_pb_kv_to_label(const uint8_t *p, size_t len, context_arg *carg, alligator_ht *dst)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	char key[OTLP_KEY_MAX] = {0};
	char val[OTLP_VAL_MAX] = {0};
	int have_key = 0;
	int have_val = 0;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;

		if (!otlp_pb_read_varint(&cur, end, &tagv))
			return 0;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);

		if (tag == 1 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				return 0;
			if (sl >= sizeof(key))
				return 0;
			memcpy(key, s, sl);
			key[sl] = '\0';
			have_key = 1;
			continue;
		}
		if (tag == 2 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				return 0;
			have_val = otlp_pb_anyvalue_to_buf(s, sl, val, sizeof(val));
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			return 0;
	}

	if (!have_key || !have_val)
		return 0;
	otlp_sanitize_attr_key(key, strlen(key));
	if (!metric_label_validator(key, strlen(key)))
		return 0;
	metric_label_value_validator_normalizer(val, strlen(val));
	if (carg && reject_metric(carg->reject, key, val))
		return 0;
	labels_hash_insert_nocache(dst, key, val);
	return 1;
}

static size_t otlp_pb_count_number_datapoints(const uint8_t *p, size_t len)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	size_t count = 0;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;

		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 1 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			count++;
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}
	return count;
}

static int otlp_pb_parse_number_datapoint(const uint8_t *p, size_t len, context_arg *carg, alligator_ht **dp_lbl, double *value)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	alligator_ht *lbl = alligator_ht_init(NULL);
	size_t added = 0;
	int have_value = 0;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;

		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 7 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			if (otlp_pb_kv_to_label(s, sl, carg, lbl))
				added++;
			continue;
		}
		if (tag == 4 && wire == 1)
		{
			uint64_t u;
			if (!otlp_pb_read_fixed64(&cur, end, &u))
				break;
			memcpy(value, &u, sizeof(*value));
			have_value = 1;
			continue;
		}
		if (tag == 6 && wire == 1)
		{
			uint64_t u;
			int64_t iv;
			if (!otlp_pb_read_fixed64(&cur, end, &u))
				break;
			memcpy(&iv, &u, sizeof(iv));
			*value = (double)iv;
			have_value = 1;
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}

	if (!have_value)
	{
		labels_hash_free(lbl);
		return 0;
	}
	if (!added)
	{
		labels_hash_free(lbl);
		lbl = NULL;
	}
	*dp_lbl = lbl;
	return 1;
}

static void otlp_pb_parse_number_data(const uint8_t *p, size_t len, const char *metric_name, int metric_ok, alligator_ht *resource_lbl, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;

		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 1 && wire == 2)
		{
			alligator_ht *dp_lbl = NULL;
			double value;
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			if (!metric_ok)
			{
				(*bad)++;
				continue;
			}
			if (!otlp_pb_parse_number_datapoint(s, sl, carg, &dp_lbl, &value))
			{
				(*bad)++;
				continue;
			}
			otlp_ingest_value_labels((char *)metric_name, value, resource_lbl, dp_lbl, carg);
			labels_hash_free(dp_lbl);
			(*ok)++;
			carg->push_accepted_lines++;
			carg->parser_status = 1;
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}
}

static int otlp_pb_metric_name(const uint8_t *p, size_t len, char *metric_name, size_t metric_name_sz)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;
		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 1 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			if (sl >= metric_name_sz)
				return 0;
			memcpy(metric_name, s, sl);
			metric_name[sl] = '\0';
			return 1;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}
	return 0;
}

static void otlp_pb_parse_metric(const uint8_t *p, size_t len, alligator_ht *resource_lbl, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	char metric_name[OTLP_NAME_MAX];
	int name_ok = otlp_pb_metric_name(p, len, metric_name, sizeof(metric_name));
	int metric_ok = 0;

	if (name_ok)
	{
		size_t nlen = strlen(metric_name);
		if (metric_name_validator_promstatsd(metric_name, nlen))
		{
			metric_name_normalizer(metric_name, nlen);
			metric_ok = 1;
		}
	}

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;
		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if ((tag == 5 || tag == 7) && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			if (!metric_ok)
				*bad += otlp_pb_count_number_datapoints(s, sl);
			else
				otlp_pb_parse_number_data(s, sl, metric_name, metric_ok, resource_lbl, carg, ok, bad);
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}
}

static void otlp_pb_parse_scope_metrics(const uint8_t *p, size_t len, alligator_ht *resource_lbl, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;
		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 2 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			otlp_pb_parse_metric(s, sl, resource_lbl, carg, ok, bad);
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}
}

static alligator_ht *otlp_pb_parse_resource_labels(const uint8_t *p, size_t len, context_arg *carg)
{
	const uint8_t *cur = p;
	const uint8_t *end = p + len;
	alligator_ht *lbl = alligator_ht_init(NULL);
	size_t added = 0;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;
		if (!otlp_pb_read_varint(&cur, end, &tagv))
			break;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 1 && wire == 2)
		{
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				break;
			if (otlp_pb_kv_to_label(s, sl, carg, lbl))
				added++;
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			break;
	}

	if (!added)
	{
		labels_hash_free(lbl);
		return NULL;
	}
	return lbl;
}

static int otlp_ingest_resource_metrics_protobuf(const char *body, size_t body_size, context_arg *carg, uint64_t *ok, uint64_t *bad)
{
	const uint8_t *cur = (const uint8_t *)body;
	const uint8_t *end = cur + body_size;
	int parsed_any = 0;

	while (cur < end)
	{
		uint64_t tagv;
		uint8_t wire;
		uint32_t tag;
		const uint8_t *s;
		size_t sl;
		if (!otlp_pb_read_varint(&cur, end, &tagv))
			return 0;
		wire = (uint8_t)(tagv & 0x7);
		tag = (uint32_t)(tagv >> 3);
		if (tag == 1 && wire == 2)
		{
			const uint8_t *rm_cur;
			const uint8_t *rm_end;
			alligator_ht *res_lbl = NULL;
			if (!otlp_pb_read_len(&cur, end, &s, &sl))
				return 0;
			parsed_any = 1;
			rm_cur = s;
			rm_end = s + sl;
			while (rm_cur < rm_end)
			{
				uint64_t rtagv;
				uint8_t rwire;
				uint32_t rtag;
				const uint8_t *rs;
				size_t rsl;
				if (!otlp_pb_read_varint(&rm_cur, rm_end, &rtagv))
					break;
				rwire = (uint8_t)(rtagv & 0x7);
				rtag = (uint32_t)(rtagv >> 3);
				if (rtag == 1 && rwire == 2)
				{
					if (!otlp_pb_read_len(&rm_cur, rm_end, &rs, &rsl))
						break;
					labels_hash_free(res_lbl);
					res_lbl = otlp_pb_parse_resource_labels(rs, rsl, carg);
					continue;
				}
				if (rtag == 2 && rwire == 2)
				{
					if (!otlp_pb_read_len(&rm_cur, rm_end, &rs, &rsl))
						break;
					otlp_pb_parse_scope_metrics(rs, rsl, res_lbl, carg, ok, bad);
					continue;
				}
				if (!otlp_pb_skip_field(&rm_cur, rm_end, rwire))
					break;
			}
			labels_hash_free(res_lbl);
			continue;
		}
		if (!otlp_pb_skip_field(&cur, end, wire))
			return 0;
	}
	return parsed_any;
}

static int otlp_headers_have_protobuf(http_reply_data *http_data)
{
	const char *h;
	if (!http_data || !http_data->headers)
		return 0;
	h = http_data->headers;
	if (strcasestr(h, "content-type:") &&
		(strcasestr(h, "application/x-protobuf") ||
		 strcasestr(h, "application/protobuf") ||
		 strcasestr(h, "application/octet-stream")))
		return 1;
	return 0;
}

static int otlp_body_looks_json(const char *body, size_t body_size)
{
	size_t i = 0;
	while (i < body_size && isspace((unsigned char)body[i]))
		i++;
	if (i >= body_size)
		return 0;
	return body[i] == '{' || body[i] == '[';
}

static void otlp_pbw_varint(string *dst, uint64_t v)
{
	uint8_t b[10];
	size_t n = 0;
	do {
		b[n] = (uint8_t)(v & 0x7f);
		v >>= 7;
		if (v)
			b[n] |= 0x80;
		n++;
	} while (v && n < sizeof(b));
	string_cat(dst, (char *)b, n);
}

static void otlp_pbw_tag(string *dst, uint32_t field, uint8_t wire)
{
	otlp_pbw_varint(dst, ((uint64_t)field << 3) | (uint64_t)(wire & 0x7));
}

static void otlp_pbw_len(string *dst, char *data, size_t len)
{
	otlp_pbw_varint(dst, len);
	if (len)
		string_cat(dst, data, len);
}

static void otlp_pbw_field_lenmsg(string *dst, uint32_t field, string *msg)
{
	otlp_pbw_tag(dst, field, 2);
	otlp_pbw_len(dst, msg->s, msg->l);
}

static void otlp_pbw_field_fixed64(string *dst, uint32_t field, uint64_t raw)
{
	uint8_t b[8];
	for (int i = 0; i < 8; i++)
		b[i] = (uint8_t)((raw >> (8 * i)) & 0xff);
	otlp_pbw_tag(dst, field, 1);
	string_cat(dst, (char *)b, 8);
}

static string *otlp_pbw_any_string(char *s, size_t len)
{
	string *msg = string_init(len + 8);
	otlp_pbw_tag(msg, 1, 2);
	otlp_pbw_len(msg, s, len);
	return msg;
}

static string *otlp_pbw_key_value_string(char *k, size_t klen, char *v, size_t vlen)
{
	string *kv = string_init(klen + vlen + 32);
	string *any = otlp_pbw_any_string(v, vlen);
	otlp_pbw_tag(kv, 1, 2);
	otlp_pbw_len(kv, k, klen);
	otlp_pbw_field_lenmsg(kv, 2, any);
	string_free(any);
	return kv;
}

static int otlp_metric_to_pb_value(metric_node *x, uint8_t *field, uint64_t *raw)
{
	double d = 0.0;
	int64_t i = 0;

	if (x->type == DATATYPE_INT)
	{
		i = x->i;
		*field = 6;
		memcpy(raw, &i, sizeof(i));
		return 1;
	}
	if (x->type == DATATYPE_UINT)
	{
		if (x->u <= (uint64_t)INT64_MAX)
		{
			i = (int64_t)x->u;
			*field = 6;
			memcpy(raw, &i, sizeof(i));
			return 1;
		}
		d = (double)x->u;
		*field = 4;
		memcpy(raw, &d, sizeof(d));
		return 1;
	}
	if (x->type == DATATYPE_DOUBLE)
	{
		d = x->d;
		*field = 4;
		memcpy(raw, &d, sizeof(d));
		return 1;
	}
	if (x->type == DATATYPE_STRING && x->s)
	{
		char *end = NULL;
		d = strtod(x->s, &end);
		if (end != x->s && end && *end == '\0')
		{
			*field = 4;
			memcpy(raw, &d, sizeof(d));
			return 1;
		}
	}
	*field = 4;
	d = 0.0;
	memcpy(raw, &d, sizeof(d));
	return 1;
}

void otlp_protobuf_serialize(metric_node *x, serializer_context *sc)
{
	labels_t *labels = x ? x->labels : NULL;
	string *req, *rm, *sm, *metric, *gauge, *dp;
	uint8_t value_field = 0;
	uint64_t value_raw = 0;

	if (!sc || !sc->str || !labels || !labels->key || !labels->key_len)
		return;
	if (!otlp_metric_to_pb_value(x, &value_field, &value_raw))
		return;

	req = string_init(256);
	rm = string_init(256);
	sm = string_init(256);
	metric = string_init(256);
	gauge = string_init(128);
	dp = string_init(256);

	for (labels_t *it = labels->next; it; it = it->next)
	{
		if (it->name_len && it->key_len)
		{
			string *kv = otlp_pbw_key_value_string(it->name, it->name_len, it->key, it->key_len);
			otlp_pbw_field_lenmsg(dp, 7, kv);
			string_free(kv);
		}
	}

	otlp_pbw_field_fixed64(dp, value_field, value_raw);
	otlp_pbw_field_lenmsg(gauge, 1, dp);

	otlp_pbw_tag(metric, 1, 2);
	otlp_pbw_len(metric, labels->key, labels->key_len);
	otlp_pbw_field_lenmsg(metric, 5, gauge);

	otlp_pbw_field_lenmsg(sm, 2, metric);
	otlp_pbw_field_lenmsg(rm, 2, sm);
	otlp_pbw_field_lenmsg(req, 1, rm);

	string_cat(sc->str, req->s, req->l);

	string_free(dp);
	string_free(gauge);
	string_free(metric);
	string_free(sm);
	string_free(rm);
	string_free(req);
}

void otlp_metrics_ingest_handler(string *response, http_reply_data *http_data, const char *configbody, context_arg *carg)
{
	const char *body = http_data ? http_data->body : configbody;
	size_t body_size = http_data ? http_data->body_size : (configbody ? strlen(configbody) : 0);
	uint8_t method = http_data ? http_data->method : HTTP_METHOD_PUT;
	char jsonbuf[192];
	char temp_resp[512];
	uint64_t lines_ok = 0;
	uint64_t lines_invalid = 0;
	json_error_t jerr;
	json_t *root = NULL;
	int parsed = 0;

	context_arg *mc = carg ? carg : (ac ? ac->system_carg : NULL);

	if (!response)
		return;

	if (method != HTTP_METHOD_POST)
	{
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 405 Method Not Allowed\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"use POST\"}\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	if (!mc)
	{
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 503 Service Unavailable\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{\"error\":\"no metric context\"}\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	mc->push_accepted_lines = 0;
	mc->parser_status = 0;

	if (!body || !body_size)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":0}\n");
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 202 Accepted\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(jsonbuf), jsonbuf);
		carglog(mc, L_INFO, "otlp_metrics_ingest: empty body\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	if (otlp_headers_have_protobuf(http_data))
	{
		parsed = otlp_ingest_resource_metrics_protobuf(body, body_size, mc, &lines_ok, &lines_invalid);
		if (!parsed)
			root = json_loadb(body, body_size, 0, &jerr);
	}
	else
	{
		if (otlp_body_looks_json(body, body_size))
			root = json_loadb(body, body_size, 0, &jerr);
		if (root)
			parsed = 1;
		else
			parsed = otlp_ingest_resource_metrics_protobuf(body, body_size, mc, &lines_ok, &lines_invalid);
	}

	if (root)
	{
		otlp_ingest_resource_metrics_json(root, mc, &lines_ok, &lines_invalid);
		json_decref(root);
	}
	else if (!parsed)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":1}\n");
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(jsonbuf), jsonbuf);
		carglog(mc, L_ERROR, "otlp_metrics_ingest: payload parse failed (expected OTLP json/protobuf)\n");
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	if (lines_ok == 0 && lines_invalid == 0)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":0}\n");
		carglog(mc, L_ERROR, "otlp_metrics_ingest: no data points in payload\n");
	}
	else
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":%" u64 ",\"linesInvalid\":%" u64 "}\n", lines_ok, lines_invalid);
		carglog(mc, L_INFO, "otlp_metrics_ingest: linesOk=%" u64 ", linesInvalid=%" u64 "\n", lines_ok, lines_invalid);
	}

	{
		uint16_t code = lines_invalid ? 400 : 202;
		const char *status = lines_invalid ? "Bad Request" : "Accepted";
		size_t jl = strlen(jsonbuf);
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 %" u16 " %s\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", code, status, jl, jsonbuf);
		string_cat(response, temp_resp, strlen(temp_resp));
	}
}

void otlp_response_catch(char *metrics, size_t size, context_arg *carg)
{
	json_error_t error;
	json_t *root = json_loads(metrics, 0, &error);
	(void)size;
	if (!root)
	{
		carglog(carg, L_ERROR, "otlp_response_catch: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *jpartialSuccess = json_object_get(root, "partialSuccess");
	if (jpartialSuccess)
		carg->parser_status = 1;
	else
		carglog(carg, L_ERROR, "otlp_response_catch: wrong answer: '%s'\n", metrics);
	json_decref(root);
}
