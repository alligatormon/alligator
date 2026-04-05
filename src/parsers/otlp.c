#include <ctype.h>
#include <stdio.h>
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
	alligator_ht *lbl = NULL;
	json_t *dp_attrs;
	double value;

	if (!otlp_number_datapoint_value(dp, &value))
		return 0;

	dp_attrs = json_object_get(dp, "attributes");
	if (resource_lbl)
		lbl = labels_dup(resource_lbl);
	if (dp_attrs && json_is_array(dp_attrs) && json_array_size(dp_attrs) > 0)
	{
		alligator_ht *dp_lbl = otlp_kv_array_to_labels(dp_attrs, carg);
		if (dp_lbl)
		{
			if (lbl)
			{
				labels_merge(lbl, dp_lbl);
				labels_hash_free(dp_lbl);
			}
			else
				lbl = dp_lbl;
		}
	}

	if (cluster_pass(carg, metric_name, lbl, &value, DATATYPE_DOUBLE, METRIC_TYPE_UNTYPED))
	{
		labels_hash_free(lbl);
		return 1;
	}
	printf("otlp_ingest_one_datapoint metric_name is %s, value is %lf\n", metric_name, value);
	metric_add(metric_name, lbl, &value, DATATYPE_DOUBLE, carg);
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

static void otlp_ingest_resource_metrics(json_t *root, context_arg *carg, uint64_t *ok, uint64_t *bad)
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

void otlp_metrics_ingest_handler(string *response, http_reply_data *http_data, const char *configbody, context_arg *carg)
{
	printf("otlp_metrics_ingest_handler\n");
	printf("http_data is %p\n", http_data);
	printf("configbody is %s\n", configbody);
	printf("carg is %p\n", carg);
	printf("response is %p\n", response);
	printf("body is %s\n", http_data ? http_data->body : configbody);
	printf("body_size is %zu\n", http_data ? http_data->body_size : (configbody ? strlen(configbody) : 0));
	printf("method is %d\n", http_data ? http_data->method : HTTP_METHOD_PUT);
	const char *body = http_data ? http_data->body : configbody;
	size_t body_size = http_data ? http_data->body_size : (configbody ? strlen(configbody) : 0);
	uint8_t method = http_data ? http_data->method : HTTP_METHOD_PUT;
	char jsonbuf[192];
	char temp_resp[512];
	uint64_t lines_ok = 0;
	uint64_t lines_invalid = 0;
	json_error_t jerr;
	json_t *root;

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

	root = json_loadb(body, body_size, 0, &jerr);
	if (!root)
	{
		snprintf(jsonbuf, sizeof(jsonbuf), "{\"linesOk\":0,\"linesInvalid\":1}\n");
		snprintf(temp_resp, sizeof(temp_resp), "HTTP/1.1 400 Bad Request\r\nServer: alligator\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(jsonbuf), jsonbuf);
		carglog(mc, L_ERROR, "otlp_metrics_ingest: json error line %d: %s\n", jerr.line, jerr.text);
		string_cat(response, temp_resp, strlen(temp_resp));
		return;
	}

	otlp_ingest_resource_metrics(root, mc, &lines_ok, &lines_invalid);
	json_decref(root);

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
	if (!root)
	{
		carglog(carg, L_ERROR, "otlp_response_catch: json error on line %d: %s\n", error.line, error.text);
		return;
	}

	json_t *jpartialSuccess = json_object_get(root, "partialSuccess");
	if (jpartialSuccess) {
		carg->parser_status = 1;
	}
	else {
		carglog(carg, L_ERROR, "otlp_response_catch: wrong answer: '%s'\n", metrics);
	}
	json_decref(root);
}
