#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
#include "common/protobuf_wire.h"
#include "common/validator.h"
#include "common/logs.h"
#include "parsers/pushgateway_proto.h"

#define PG_NAME_MAX 255
#define PG_HELP_MAX 2048
#define PG_LABEL_MAX 255
#define PG_MAX_LABELS 64
#define PG_MAX_QUANTILES 128
#define PG_MAX_BUCKETS 128

#define PG_TYPE_COUNTER 0
#define PG_TYPE_GAUGE 1
#define PG_TYPE_SUMMARY 2
#define PG_TYPE_UNTYPED 3
#define PG_TYPE_HISTOGRAM 4
#define PG_TYPE_GAUGE_HISTOGRAM 5

typedef struct pg_slice {
	const uint8_t *data;
	size_t len;
} pg_slice;

typedef struct pg_quantile {
	double quantile;
	double value;
} pg_quantile;

typedef struct pg_bucket {
	uint64_t count;
	double upper_bound;
} pg_bucket;

typedef struct pg_metric {
	pg_slice label_name[PG_MAX_LABELS];
	pg_slice label_value[PG_MAX_LABELS];
	size_t label_count;
	int too_many;
	double value;
	int has_value;
	uint64_t count;
	int has_count;
	double sum;
	int has_sum;
	pg_quantile quantiles[PG_MAX_QUANTILES];
	size_t quantile_count;
	pg_bucket buckets[PG_MAX_BUCKETS];
	size_t bucket_count;
} pg_metric;

static int pg_read_tag(const uint8_t **p, const uint8_t *end, uint32_t *field, uint8_t *wire)
{
	uint64_t tag;
	if (!pbwire_read_varint(p, end, &tag))
		return 0;
	*field = (uint32_t)(tag >> 3);
	*wire = (uint8_t)(tag & 0x7);
	return *field != 0;
}

static void pg_double_from_bits(uint64_t raw, double *value)
{
	memcpy(value, &raw, sizeof(*value));
}

static void pg_shortest_double(char *buf, size_t cap, double value)
{
	if (!buf || !cap)
		return;

	if (isnan(value))
	{
		snprintf(buf, cap, "NaN");
		return;
	}
	if (isinf(value))
	{
		snprintf(buf, cap, value < 0 ? "-Inf" : "+Inf");
		return;
	}

	if (value == 0)
	{
		snprintf(buf, cap, signbit(value) ? "-0" : "0");
		return;
	}

	/* Shortest round-tripping digits via %e, laid out like Go's
	 * strconv.FormatFloat(v, 'g', -1, 64): exponent form only when exp < -4 || exp >= 6
	 * (10 -> "10", 100000 -> "100000", 1e6 -> "1e+06"). C's %g would print 10 as "1e+01",
	 * which breaks le/quantile label parity with text pushes. */
	char e[40];
	for (int precision = 0; precision <= 16; ++precision)
	{
		snprintf(e, sizeof(e), "%.*e", precision, value);
		if (strtod(e, NULL) == value)
			break;
	}

	const char *s = e;
	int neg = 0;
	if (*s == '-')
	{
		neg = 1;
		++s;
	}

	char digits[20];
	size_t nd = 0;
	for (; *s && *s != 'e' && nd < sizeof(digits) - 1; ++s)
		if (*s >= '0' && *s <= '9')
			digits[nd++] = *s;
	digits[nd] = 0;
	while (nd > 1 && digits[nd - 1] == '0')
		digits[--nd] = 0;
	while (*s && *s != 'e')
		++s;

	int exp = (*s == 'e') ? atoi(s + 1) : 0;

	char out[96];
	size_t o = 0;
	if (neg)
		out[o++] = '-';

	if (exp < -4 || exp >= 6)
	{
		out[o++] = digits[0];
		if (nd > 1)
		{
			out[o++] = '.';
			memcpy(out + o, digits + 1, nd - 1);
			o += nd - 1;
		}
		o += (size_t)snprintf(out + o, sizeof(out) - o, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
	}
	else if (exp >= 0)
	{
		for (size_t i = 0; i < (size_t)exp + 1; ++i)
			out[o++] = i < nd ? digits[i] : '0';
		if (nd > (size_t)exp + 1)
		{
			out[o++] = '.';
			memcpy(out + o, digits + exp + 1, nd - exp - 1);
			o += nd - exp - 1;
		}
	}
	else
	{
		out[o++] = '0';
		out[o++] = '.';
		for (int i = 0; i < -exp - 1; ++i)
			out[o++] = '0';
		memcpy(out + o, digits, nd);
		o += nd;
	}
	out[o] = 0;
	snprintf(buf, cap, "%s", out);
}

static void pg_cat_escaped(string *out, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; ++i)
	{
		char c = (char)data[i];
		if (c == '\\' || c == '"')
		{
			string_cat(out, "\\", 1);
			string_cat(out, &c, 1);
		}
		else if (c == '\n')
			string_cat(out, "\\n", 2);
		else
			string_cat(out, &c, 1);
	}
}

static int pg_label_name_ok(const pg_slice *name)
{
	if (!name->data || !name->len || name->len >= PG_LABEL_MAX)
		return 0;

	char buf[PG_LABEL_MAX];
	for (size_t i = 0; i < name->len; ++i)
	{
		unsigned char c = name->data[i];
		if (!c || c < 0x20 || c == 0x7f)
			return 0;
		buf[i] = (char)c;
	}
	buf[name->len] = 0;
	return metric_name_validator(buf, name->len);
}

static int pg_label_value_ok(const pg_slice *value)
{
	if (!value->data && value->len)
		return 0;
	if (value->len >= PG_LABEL_MAX)
		return 0;

	for (size_t i = 0; i < value->len; ++i)
	{
		unsigned char c = value->data[i];
		if (!c || c == 0x7f || (c < 0x20 && c != '\n'))
			return 0;
	}
	return 1;
}

static int pg_family_name(const uint8_t *data, size_t len, char *out)
{
	if (!data || !len || len >= PG_NAME_MAX)
		return 0;
	if (memchr(data, 0, len))
		return 0;

	memcpy(out, data, len);
	out[len] = 0;
	if (!metric_name_validator_promstatsd(out, len))
		return 0;

	prometheus_metric_name_normalizer(out, len);
	return 1;
}

static size_t pg_help_copy(const uint8_t *data, size_t len, char *out, size_t cap)
{
	if (!data || !len || cap < 2)
		return 0;
	if (len > cap - 1)
		len = cap - 1;

	size_t copy = 0;
	for (size_t i = 0; i < len && data[i]; ++i)
	{
		/* keep '\n' (escaped on emit); other control chars would break line framing */
		unsigned char c = data[i];
		out[copy++] = (c < 0x20 && c != '\n') ? ' ' : (char)c;
	}
	out[copy] = 0;
	return copy;
}

static const char *pg_type_keyword(uint32_t type)
{
	switch (type)
	{
	case PG_TYPE_COUNTER: return "counter";
	case PG_TYPE_GAUGE: return "gauge";
	case PG_TYPE_SUMMARY: return "summary";
	case PG_TYPE_UNTYPED: return "untyped";
	case PG_TYPE_HISTOGRAM: return "histogram";
	default: return NULL;
	}
}

static int pg_parse_family_meta(const uint8_t *p, const uint8_t *end,
	pg_slice *name, pg_slice *help, uint32_t *type)
{
	name->data = NULL;
	name->len = 0;
	help->data = NULL;
	help->len = 0;
	*type = PG_TYPE_COUNTER;

	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 2)
		{
			if (!pbwire_read_len(&p, end, &name->data, &name->len))
				return 0;
		}
		else if (field == 2 && wire == 2)
		{
			if (!pbwire_read_len(&p, end, &help->data, &help->len))
				return 0;
		}
		else if (field == 3 && wire == 0)
		{
			uint64_t value;
			if (!pbwire_read_varint(&p, end, &value))
				return 0;
			*type = (uint32_t)value;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_label_pair(const uint8_t *p, const uint8_t *end, pg_slice *name, pg_slice *value)
{
	name->data = NULL;
	name->len = 0;
	value->data = NULL;
	value->len = 0;

	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 2)
		{
			if (!pbwire_read_len(&p, end, &name->data, &name->len))
				return 0;
		}
		else if (field == 2 && wire == 2)
		{
			if (!pbwire_read_len(&p, end, &value->data, &value->len))
				return 0;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_single_value(const uint8_t *p, const uint8_t *end, double *value, int *has_value)
{
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, value);
			*has_value = 1;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_quantile(const uint8_t *p, const uint8_t *end, double *quantile, double *value, int *present)
{
	int has_quantile = 0;
	int has_value = 0;

	*present = 0;
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, quantile);
			has_quantile = 1;
		}
		else if (field == 2 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, value);
			has_value = 1;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	*present = has_quantile && has_value;
	return 1;
}

static int pg_decode_bucket(const uint8_t *p, const uint8_t *end, uint64_t *count, double *upper_bound, int *present)
{
	int has_count = 0;
	int has_upper = 0;

	*present = 0;
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 0)
		{
			if (!pbwire_read_varint(&p, end, count))
				return 0;
			has_count = 1;
		}
		else if (field == 2 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, upper_bound);
			has_upper = 1;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	*present = has_count && has_upper;
	return 1;
}

static int pg_decode_summary(const uint8_t *p, const uint8_t *end, pg_metric *metric)
{
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 0)
		{
			if (!pbwire_read_varint(&p, end, &metric->count))
				return 0;
			metric->has_count = 1;
		}
		else if (field == 2 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, &metric->sum);
			metric->has_sum = 1;
		}
		else if (field == 3 && wire == 2)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;

			double quantile = 0;
			double value = 0;
			int present;
			if (!pg_decode_quantile(start, start + len, &quantile, &value, &present))
				return 0;
			if (!present)
				continue;

			if (metric->quantile_count >= PG_MAX_QUANTILES)
			{
				metric->too_many = 1;
				continue;
			}
			metric->quantiles[metric->quantile_count].quantile = quantile;
			metric->quantiles[metric->quantile_count].value = value;
			++metric->quantile_count;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_histogram(const uint8_t *p, const uint8_t *end, pg_metric *metric)
{
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 0)
		{
			if (!pbwire_read_varint(&p, end, &metric->count))
				return 0;
			metric->has_count = 1;
		}
		else if (field == 2 && wire == 1)
		{
			uint64_t raw;
			if (!pbwire_read_fixed64(&p, end, &raw))
				return 0;
			pg_double_from_bits(raw, &metric->sum);
			metric->has_sum = 1;
		}
		else if (field == 3 && wire == 2)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;

			uint64_t count = 0;
			double upper_bound = 0;
			int present;
			if (!pg_decode_bucket(start, start + len, &count, &upper_bound, &present))
				return 0;
			if (!present)
				continue;

			if (metric->bucket_count >= PG_MAX_BUCKETS)
			{
				metric->too_many = 1;
				continue;
			}
			metric->buckets[metric->bucket_count].count = count;
			metric->buckets[metric->bucket_count].upper_bound = upper_bound;
			++metric->bucket_count;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_metric(const uint8_t *p, const uint8_t *end, uint32_t type, pg_metric *metric)
{
	memset(metric, 0, sizeof(*metric));

	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 1 && wire == 2)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;

			pg_slice name;
			pg_slice value;
			if (!pg_decode_label_pair(start, start + len, &name, &value))
				return 0;

			if (metric->label_count >= PG_MAX_LABELS)
			{
				metric->too_many = 1;
				continue;
			}
			metric->label_name[metric->label_count] = name;
			metric->label_value[metric->label_count] = value;
			++metric->label_count;
		}
		else if (field == 2 && wire == 2 && type == PG_TYPE_GAUGE)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			if (!pg_decode_single_value(start, start + len, &metric->value, &metric->has_value))
				return 0;
		}
		else if (field == 3 && wire == 2 && type == PG_TYPE_COUNTER)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			if (!pg_decode_single_value(start, start + len, &metric->value, &metric->has_value))
				return 0;
		}
		else if (field == 4 && wire == 2 && type == PG_TYPE_SUMMARY)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			if (!pg_decode_summary(start, start + len, metric))
				return 0;
		}
		else if (field == 5 && wire == 2 && type == PG_TYPE_UNTYPED)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			if (!pg_decode_single_value(start, start + len, &metric->value, &metric->has_value))
				return 0;
		}
		else if (field == 7 && wire == 2 && type == PG_TYPE_HISTOGRAM)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			if (!pg_decode_histogram(start, start + len, metric))
				return 0;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_metric_valid(const pg_metric *metric, uint32_t type)
{
	if (metric->too_many)
		return 0;

	for (size_t i = 0; i < metric->label_count; ++i)
		if (!pg_label_name_ok(&metric->label_name[i]) ||
		    !pg_label_value_ok(&metric->label_value[i]))
			return 0;

	switch (type)
	{
	case PG_TYPE_COUNTER:
	case PG_TYPE_GAUGE:
	case PG_TYPE_UNTYPED:
		return metric->has_value;
	case PG_TYPE_SUMMARY:
		return metric->has_count || metric->has_sum || metric->quantile_count;
	case PG_TYPE_HISTOGRAM:
		return metric->has_count || metric->has_sum || metric->bucket_count;
	default:
		return 0;
	}
}

static void pg_emit_meta(string *out, const char *name, const char *help, size_t help_len, const char *type)
{
	if (help_len)
	{
		string_cat(out, "# HELP ", 7);
		string_cat(out, (char *)name, strlen(name));
		string_cat(out, " ", 1);
		pg_cat_escaped(out, (const uint8_t *)help, help_len);
		string_cat(out, "\n", 1);
	}

	string_cat(out, "# TYPE ", 7);
	string_cat(out, (char *)name, strlen(name));
	string_cat(out, " ", 1);
	string_cat(out, (char *)type, strlen(type));
	string_cat(out, "\n", 1);
}

static void pg_emit_line(string *out, const char *name, const pg_metric *metric,
	const char *extra_name, const char *extra_value, const char *number)
{
	string_cat(out, (char *)name, strlen(name));

	int first = 1;
	for (size_t i = 0; i < metric->label_count; ++i)
	{
		char label_name[PG_LABEL_MAX];
		size_t len = metric->label_name[i].len;
		memcpy(label_name, metric->label_name[i].data, len);
		label_name[len] = 0;
		prometheus_metric_name_normalizer(label_name, len);

		if (first)
		{
			string_cat(out, "{", 1);
			first = 0;
		}
		else
			string_cat(out, ",", 1);

		string_cat(out, label_name, len);
		string_cat(out, "=\"", 2);
		pg_cat_escaped(out, metric->label_value[i].data, metric->label_value[i].len);
		string_cat(out, "\"", 1);
	}

	if (extra_name)
	{
		if (first)
		{
			string_cat(out, "{", 1);
			first = 0;
		}
		else
			string_cat(out, ",", 1);
		string_cat(out, (char *)extra_name, strlen(extra_name));
		string_cat(out, "=\"", 2);
		string_cat(out, (char *)extra_value, strlen(extra_value));
		string_cat(out, "\"", 1);
	}

	if (!first)
		string_cat(out, "}", 1);

	string_cat(out, " ", 1);
	string_cat(out, (char *)number, strlen(number));
	string_cat(out, "\n", 1);
}

static void pg_emit_metric(string *out, const char *name, uint32_t type, const pg_metric *metric)
{
	char number[64];
	char extra[64];
	char suffixed[PG_NAME_MAX + 16];

	switch (type)
	{
	case PG_TYPE_COUNTER:
	case PG_TYPE_GAUGE:
	case PG_TYPE_UNTYPED:
		pg_shortest_double(number, sizeof(number), metric->value);
		pg_emit_line(out, name, metric, NULL, NULL, number);
		break;

	case PG_TYPE_SUMMARY:
		for (size_t i = 0; i < metric->quantile_count; ++i)
		{
			pg_shortest_double(extra, sizeof(extra), metric->quantiles[i].quantile);
			pg_shortest_double(number, sizeof(number), metric->quantiles[i].value);
			pg_emit_line(out, name, metric, "quantile", extra, number);
		}
		if (metric->has_sum)
		{
			snprintf(suffixed, sizeof(suffixed), "%s_sum", name);
			pg_shortest_double(number, sizeof(number), metric->sum);
			pg_emit_line(out, suffixed, metric, NULL, NULL, number);
		}
		if (metric->has_count)
		{
			snprintf(suffixed, sizeof(suffixed), "%s_count", name);
			snprintf(number, sizeof(number), "%" PRIu64, metric->count);
			pg_emit_line(out, suffixed, metric, NULL, NULL, number);
		}
		break;

	case PG_TYPE_HISTOGRAM:
	{
		int inf_seen = 0;
		snprintf(suffixed, sizeof(suffixed), "%s_bucket", name);
		for (size_t i = 0; i < metric->bucket_count; ++i)
		{
			if (isinf(metric->buckets[i].upper_bound) && metric->buckets[i].upper_bound > 0)
				inf_seen = 1;
			pg_shortest_double(extra, sizeof(extra), metric->buckets[i].upper_bound);
			snprintf(number, sizeof(number), "%" PRIu64, metric->buckets[i].count);
			pg_emit_line(out, suffixed, metric, "le", extra, number);
		}
		/* client_golang drops the +Inf bucket from the protobuf (it is implicit) and expfmt's
		 * text encoder re-adds it from sample_count. Do the same, or histogram_quantile on a
		 * protobuf push has no +Inf bucket to work with. */
		if (!inf_seen && (metric->bucket_count || metric->has_count))
		{
			/* no sample_count: fall back to the last (cumulative) bucket so +Inf is never below it */
			uint64_t inf_count = metric->has_count ? metric->count :
				(metric->bucket_count ? metric->buckets[metric->bucket_count - 1].count : 0);
			snprintf(number, sizeof(number), "%" PRIu64, inf_count);
			pg_emit_line(out, suffixed, metric, "le", "+Inf", number);
		}
		if (metric->has_sum)
		{
			snprintf(suffixed, sizeof(suffixed), "%s_sum", name);
			pg_shortest_double(number, sizeof(number), metric->sum);
			pg_emit_line(out, suffixed, metric, NULL, NULL, number);
		}
		if (metric->has_count)
		{
			snprintf(suffixed, sizeof(suffixed), "%s_count", name);
			snprintf(number, sizeof(number), "%" PRIu64, metric->count);
			pg_emit_line(out, suffixed, metric, NULL, NULL, number);
		}
		break;
	}
	}
}

static int pg_count_metrics(const uint8_t *p, const uint8_t *end, uint64_t *count)
{
	while (p < end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, end, &field, &wire))
			return 0;

		if (field == 4 && wire == 2)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, end, &start, &len))
				return 0;
			++*count;
		}
		else if (!pbwire_skip_field(&p, end, wire))
			return 0;
	}
	return 1;
}

static int pg_decode_family(const uint8_t *family, const uint8_t *family_end, string *out,
	uint64_t *families, uint64_t *series, uint64_t *rejected)
{
	pg_slice name;
	pg_slice help;
	uint32_t type;

	if (!pg_parse_family_meta(family, family_end, &name, &help, &type))
		return 0;

	char family_name[PG_NAME_MAX];
	if (!pg_family_name(name.data, name.len, family_name))
	{
		uint64_t family_series = 0;
		if (!pg_count_metrics(family, family_end, &family_series))
			return 0;
		glog(L_WARN, "pushgateway protobuf: skipping family with invalid name (len=%zu, series=%" PRIu64 ")\n",
			name.len, family_series);
		*rejected += family_series;
		return 1;
	}

	char help_buf[PG_HELP_MAX];
	size_t help_len = pg_help_copy(help.data, help.len, help_buf, sizeof(help_buf));

	if (type == PG_TYPE_GAUGE_HISTOGRAM || !pg_type_keyword(type))
	{
		uint64_t family_series = 0;
		if (!pg_count_metrics(family, family_end, &family_series))
			return 0;

		if (type == PG_TYPE_GAUGE_HISTOGRAM)
			glog(L_WARN, "pushgateway protobuf: skipping GAUGE_HISTOGRAM family '%s' (series=%" PRIu64 ")\n",
				family_name, family_series);
		else
			glog(L_WARN, "pushgateway protobuf: skipping unknown MetricType %u family '%s' (series=%" PRIu64 ")\n",
				type, family_name, family_series);

		*rejected += family_series;
		return 1;
	}

	const char *type_keyword = pg_type_keyword(type);

	int meta_emitted = 0;
	const uint8_t *p = family;
	while (p < family_end)
	{
		uint32_t field;
		uint8_t wire;
		if (!pg_read_tag(&p, family_end, &field, &wire))
			return 0;

		if (field == 4 && wire == 2)
		{
			const uint8_t *start;
			size_t len;
			if (!pbwire_read_len(&p, family_end, &start, &len))
				return 0;

			pg_metric metric;
			if (!pg_decode_metric(start, start + len, type, &metric))
				return 0;

			if (!pg_metric_valid(&metric, type))
			{
				glog(L_WARN, "pushgateway protobuf: skipping invalid sample in family '%s'\n", family_name);
				++*rejected;
				continue;
			}

			if (!meta_emitted)
			{
				pg_emit_meta(out, family_name, help_buf, help_len, type_keyword);
				meta_emitted = 1;
			}
			pg_emit_metric(out, family_name, type, &metric);
			++*series;
		}
		else if (!pbwire_skip_field(&p, family_end, wire))
			return 0;
	}

	if (meta_emitted)
		++*families;

	return 1;
}

int pushgateway_proto_delimited_to_text(const char *body, size_t size, string *out,
	uint64_t *families, uint64_t *series, uint64_t *rejected)
{
	uint64_t local_families = 0;
	uint64_t local_series = 0;
	uint64_t local_rejected = 0;

	if (!out)
		return 0;

	if (families)
		*families = 0;
	if (series)
		*series = 0;
	if (rejected)
		*rejected = 0;

	if (!body || !size)
		return 1;

	const uint8_t *p = (const uint8_t *)body;
	const uint8_t *end = p + size;

	while (p < end)
	{
		uint64_t family_len;
		if (!pbwire_read_varint(&p, end, &family_len))
			return 0;
		if (family_len > (uint64_t)(end - p))
			return 0;

		const uint8_t *family_end = p + family_len;
		if (!pg_decode_family(p, family_end, out, &local_families, &local_series, &local_rejected))
			return 0;
		p = family_end;
	}

	if (families)
		*families = local_families;
	if (series)
		*series = local_series;
	if (rejected)
		*rejected = local_rejected;

	return 1;
}
